shared.makeHeader("Formulaires", page)
route.ctx = page.text("id", "Identifiant", {value: route.ctx, compact: true, readonly: true}).value

Array.from(util.mapRLE(shared.echelles, echelle => echelle.category, (category, offset, len) => {
    page.section(category, () => {
        let buttons = util.mapRange(offset, offset + len, idx => {
            let echelle = shared.echelles[idx];

            return html`
                <div style="width: 80%; margin: 2px auto;" class="af_multi">
                    <button class="af_button" type="button"
                            style="width: 75%; margin-right: 40px; vertical-align: middle;"
                            @click=${() => go(echelle.form)}>${echelle.name}</button>

                    <input id=${'md_lock' + idx} class="md_lock" type="checkbox"
                           data-form=${echelle.form} @click=${() => page.restart()} />
                    <label for=${'md_lock' + idx} >🔒\uFE0E</label>
                </div>
            `;
        })

        page.output(html`<div style="text-align: center;">${Array.from(buttons)}</div>`)
    })
}))

page.output(html`
    <br/>
    <button class="md_button" ?disabled=${!document.querySelector('.md_lock:checked')}
            @click=${enableLock}>🔒\uFE0E Verrouiller</button>
    <button class="md_button" @click=${e => go("Accueil")}>Retour à l'accueil</button>
`)

function enableLock(e) {
    let checkboxes = document.querySelectorAll('.md_lock:checked');
    let forms = Array.from(checkboxes).map(el => el.dataset.form);
    let urls = forms.map(form => `${env.base_url}app/${form}/${form}/`);

    user.showLockDialog(e, urls, {ctx: route.ctx});
}
