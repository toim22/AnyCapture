//! A small ZIP writer, enough for Export › Export as ZIP: the files one after another with UTF-8 names and the local
//! time of each, deflated when that makes them smaller (BMP, TIFF) and stored as they are otherwise (PNG, JPEG, GIF,
//! MP4 are compressed already). Explorer, 7-Zip and every mail program open it. No ZIP64: an archive stays under 4 GB.

use std::fs::File;
use std::io::{BufWriter, Seek, Write};
use std::path::Path;
use std::time::SystemTime;

use flate2::Compression;
use flate2::write::DeflateEncoder;

struct Entry {
    name: Vec<u8>,
    crc: u32,
    method: u16,
    packed: u32,
    size: u32,
    time: u16,
    date: u16,
    offset: u32,
}

pub struct ZipWriter {
    out: BufWriter<File>,
    entries: Vec<Entry>,
}

fn too_big() -> std::io::Error {
    std::io::Error::other("the archive would pass 4 GB")
}

/// MS-DOS date and time (local time, 2-second steps) of a moment, as ZIP keeps them.
fn dos_time(t: SystemTime) -> (u16, u16) {
    use windows::Win32::Foundation::{FILETIME, SYSTEMTIME};
    use windows::Win32::System::Time::{FileTimeToSystemTime, SystemTimeToTzSpecificLocalTime};
    let ticks = t.duration_since(SystemTime::UNIX_EPOCH).map(|d| d.as_nanos() / 100).unwrap_or(0) as u64 + 116_444_736_000_000_000;
    let ft = FILETIME { dwLowDateTime: ticks as u32, dwHighDateTime: (ticks >> 32) as u32 };
    let (mut utc, mut local) = (SYSTEMTIME::default(), SYSTEMTIME::default());
    unsafe {
        if FileTimeToSystemTime(&ft, &mut utc).is_err() || SystemTimeToTzSpecificLocalTime(None, &utc, &mut local).is_err() {
            return (0, 0x21); // 1980-01-01
        }
    }
    let year = local.wYear.clamp(1980, 2107) - 1980;
    ((local.wHour << 11) | (local.wMinute << 5) | (local.wSecond / 2), (year << 9) | (local.wMonth << 5) | local.wDay)
}

impl ZipWriter {
    pub fn create(path: &Path) -> std::io::Result<ZipWriter> {
        Ok(ZipWriter { out: BufWriter::new(File::create(path)?), entries: Vec::new() })
    }

    /// Adds a file named `name` (a path inside the archive, with `/`) holding `data`, last changed at `modified`.
    pub fn add(&mut self, name: &str, data: &[u8], modified: SystemTime) -> std::io::Result<()> {
        let size = u32::try_from(data.len()).map_err(|_| too_big())?;
        let crc = crc32fast::hash(data);
        let mut enc = DeflateEncoder::new(Vec::new(), Compression::default());
        enc.write_all(data)?;
        let deflated = enc.finish()?;
        // stored when deflating gains little (already compressed formats)
        let (method, body): (u16, &[u8]) = if deflated.len() + data.len() / 50 < data.len() { (8, &deflated) } else { (0, data) };
        let offset = u32::try_from(self.out.stream_position()?).map_err(|_| too_big())?;
        let (time, date) = dos_time(modified);
        let e = Entry { name: name.as_bytes().to_vec(), crc, method, packed: body.len() as u32, size, time, date, offset };
        let o = &mut self.out;
        o.write_all(&0x0403_4b50u32.to_le_bytes())?;
        o.write_all(&20u16.to_le_bytes())?; // version needed: 2.0
        o.write_all(&0x0800u16.to_le_bytes())?; // the name is UTF-8
        o.write_all(&e.method.to_le_bytes())?;
        o.write_all(&e.time.to_le_bytes())?;
        o.write_all(&e.date.to_le_bytes())?;
        o.write_all(&e.crc.to_le_bytes())?;
        o.write_all(&e.packed.to_le_bytes())?;
        o.write_all(&e.size.to_le_bytes())?;
        o.write_all(&(e.name.len() as u16).to_le_bytes())?;
        o.write_all(&0u16.to_le_bytes())?; // no extra field
        o.write_all(&e.name)?;
        o.write_all(body)?;
        self.entries.push(e);
        Ok(())
    }

    /// Writes the central directory; the archive is complete after this.
    pub fn finish(mut self) -> std::io::Result<()> {
        let start = u32::try_from(self.out.stream_position()?).map_err(|_| too_big())?;
        for e in &self.entries {
            let o = &mut self.out;
            o.write_all(&0x0201_4b50u32.to_le_bytes())?;
            o.write_all(&20u16.to_le_bytes())?; // made by: 2.0, MS-DOS attributes
            o.write_all(&20u16.to_le_bytes())?;
            o.write_all(&0x0800u16.to_le_bytes())?;
            o.write_all(&e.method.to_le_bytes())?;
            o.write_all(&e.time.to_le_bytes())?;
            o.write_all(&e.date.to_le_bytes())?;
            o.write_all(&e.crc.to_le_bytes())?;
            o.write_all(&e.packed.to_le_bytes())?;
            o.write_all(&e.size.to_le_bytes())?;
            o.write_all(&(e.name.len() as u16).to_le_bytes())?;
            o.write_all(&[0u8; 8])?; // extra, comment, disk, internal attributes
            o.write_all(&0u32.to_le_bytes())?; // external attributes
            o.write_all(&e.offset.to_le_bytes())?;
            o.write_all(&e.name)?;
        }
        let end = u32::try_from(self.out.stream_position()?).map_err(|_| too_big())?;
        let n = u16::try_from(self.entries.len()).map_err(|_| std::io::Error::other("more than 65 535 files"))?;
        let o = &mut self.out;
        o.write_all(&0x0605_4b50u32.to_le_bytes())?;
        o.write_all(&[0u8; 4])?; // this disk, the directory's disk
        o.write_all(&n.to_le_bytes())?;
        o.write_all(&n.to_le_bytes())?;
        o.write_all(&(end - start).to_le_bytes())?;
        o.write_all(&start.to_le_bytes())?;
        o.write_all(&0u16.to_le_bytes())?; // no comment
        o.flush()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn writes_an_archive_with_stored_and_deflated_files() {
        let dir = std::env::temp_dir().join(format!("anycapture-zip-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("t.zip");
        let mut z = ZipWriter::create(&path).unwrap();
        let text = "AnyCapture ".repeat(500); // compresses
        let mut x = 0x2545_F491_4F6C_DD1Du64;
        let noise: Vec<u8> = (0..4000)
            .map(|_| {
                x ^= x << 13;
                x ^= x >> 7;
                x ^= x << 17;
                (x >> 24) as u8
            })
            .collect(); // does not
        z.add("Captura_1.bmp", text.as_bytes(), SystemTime::now()).unwrap();
        z.add("pasta/Vídeo 2.mp4", &noise, SystemTime::now()).unwrap();
        z.finish().unwrap();
        let b = std::fs::read(&path).unwrap();
        assert_eq!(&b[..4], b"PK\x03\x04");
        assert_eq!(u16::from_le_bytes([b[8], b[9]]), 8, "the text is deflated");
        let eocd = b.len() - 22;
        assert_eq!(&b[eocd..eocd + 4], b"PK\x05\x06");
        assert_eq!(u16::from_le_bytes([b[eocd + 10], b[eocd + 11]]), 2);
        let dir_at = u32::from_le_bytes(b[eocd + 16..eocd + 20].try_into().unwrap()) as usize;
        assert_eq!(&b[dir_at..dir_at + 4], b"PK\x01\x02");
        // the second entry is stored: its packed size equals its size
        let second = b.windows(4).rposition(|w| w == b"PK\x01\x02").unwrap();
        let packed = u32::from_le_bytes(b[second + 20..second + 24].try_into().unwrap());
        let size = u32::from_le_bytes(b[second + 24..second + 28].try_into().unwrap());
        assert_eq!((packed, size), (4000, 4000));
        assert!(b.windows("pasta/Vídeo 2.mp4".len()).any(|w| w == "pasta/Vídeo 2.mp4".as_bytes()));
        let _ = std::fs::remove_dir_all(&dir);
    }
}
