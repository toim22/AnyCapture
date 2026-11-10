//! Help › Report a problem… and Help › Send a suggestion or feedback… in both programs: a form (title, description, the
//! steps or the reason, the system information), then the GitHub issue, labeled `bug` or `enhancement`
//! (`snapcore::project::send_issue`), and what became of it.

use snapcore::project::{self, Issue, IssueKind, Sent};
use snapcore::wide::to_wide;
use windows::Win32::Foundation::HWND;
use windows::Win32::UI::WindowsAndMessaging::{IDYES, MB_ICONINFORMATION, MB_ICONWARNING, MB_YESNO, MESSAGEBOX_STYLE, MessageBoxW};
use windows::core::PCWSTR;

use crate::form::{self, Field};

fn tr(s: &str) -> String {
    snapcore::i18n::tr_str(s)
}

fn message(owner: isize, text: &str, style: MESSAGEBOX_STYLE) -> i32 {
    let (t, c) = (to_wide(text), to_wide("AnyCapture"));
    unsafe { MessageBoxW((owner != 0).then(|| HWND(owner as _)), PCWSTR(t.as_ptr()), PCWSTR(c.as_ptr()), style) }.0
}

/// Opens the form on a thread of its own (it runs its own message loop and the sending takes a few seconds).
/// `program` names the program in the issue ("AnyCapture", "AnyCapture Editor").
pub fn open(owner: isize, kind: IssueKind, program: &str, app_version: &str) {
    let (program, app_version) = (program.to_owned(), app_version.to_owned());
    std::thread::spawn(move || run(owner, kind, program, app_version));
}

fn run(owner: isize, kind: IssueKind, program: String, app_version: String) {
    let bug = kind == IssueKind::Bug;
    let mut f = vec![
        Field::Label(if bug {
            tr("Descreva o problema. Ele é enviado ao projeto AnyCapture no GitHub como um novo relato (bug).")
        } else {
            tr("Conte sua ideia ou o que acha do AnyCapture. Ela é enviada ao projeto AnyCapture no GitHub como uma sugestão.")
        }),
        Field::Text { label: tr("Título"), text: String::new() },
        Field::Memo { label: if bug { tr("O que aconteceu?") } else { tr("Sua sugestão ou feedback") }, text: String::new(), rows: 5 },
        Field::Memo {
            label: if bug { tr("Passos para reproduzir (opcional)") } else { tr("Por que ajudaria? (opcional)") },
            text: String::new(),
            rows: 3,
        },
        Field::Check { label: tr("Incluir a versão do Windows e o idioma"), value: bug },
        Field::Label(format!("AnyCapture {app_version} · {}", project::system_info())),
    ];
    let validate = |f: &[Field]| -> String {
        if f[1].text().trim().is_empty() || f[2].text().trim().is_empty() {
            tr("Preencha o título e a descrição.")
        } else {
            String::new()
        }
    };
    let title = if bug { tr("Relatar um problema") } else { tr("Enviar sugestão ou feedback") };
    if !form::run_ex(owner, &title, &mut f, 560, Some(&validate)) {
        return;
    }
    let issue = Issue {
        title: f[1].text().trim().to_owned(),
        text: f[2].text().to_owned(),
        more: f[3].text().to_owned(),
        app_version,
        program,
        system: f[4].on(),
    };
    match project::send_issue(kind, &issue) {
        Sent::Created(url) => {
            let thanks = if bug { tr("Obrigado! Seu relato foi enviado ao projeto AnyCapture no GitHub:") } else {
                tr("Obrigado! Sua sugestão foi enviada ao projeto AnyCapture no GitHub:")
            };
            if message(owner, &format!("{thanks}\n\n{url}\n\n{}", tr("Abrir no navegador?")), MB_YESNO | MB_ICONINFORMATION) == IDYES.0 {
                project::open_url(&url);
            }
        }
        Sent::Browser => {
            message(owner, &tr("O GitHub abriu no navegador com tudo já preenchido. Entre na sua conta do GitHub se for pedido, revise e clique em \"Create\" para enviar."),
                    MB_ICONINFORMATION);
        }
        Sent::Failed(issues) => {
            message(owner, &format!("{}\n\n{issues}", tr("Não foi possível enviar nem abrir o navegador. Envie pelo endereço:")), MB_ICONWARNING);
        }
    }
}
