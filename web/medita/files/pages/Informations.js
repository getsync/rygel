shared.makeHeader("Patient", page)
route.ctx = page.text("id", "Identifiant", {value: route.ctx, compact: true, readonly: true}).value

page.section("Informations administratives", () => {
    page.text("nom", "Nom")
    page.text("prenom", "Prénom")
    page.date("ddn", "Date de naissance")
})

form.output(html`
    <br/>
    <button class="md_button" ?disabled=${!page.hasChanged() || !page.isValid()}
                @click=${page.submit}>Enregistrer</button>
    <button class="md_button" @click=${e => go("Accueil")}>Retour à l'accueil</button>
`)
