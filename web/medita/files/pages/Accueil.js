form.output(html`
    <img src="${env.base_url}files/resources/logo.png"
         style="display: block; height: 160px; margin: 2em auto;">

    <table class="md_table">
        <thead>
            <tr>
                <th>Identifiant</th>
                <th>Nom</th>
                <th>Prénom</th>
                <th></th>
            </tr>
        </thead>
        <tbody>${until(listPatients(), '')}</tbody>
    </table>
    
    <br/><br/>
    <button class="md_button" @click=${createPatient}>Nouveau patient</button>
`)

async function listPatients() {
    let patients = await vrec.loadAll('Patient')

    if (patients.length) {
        return patients.map(patient => {
            let href = nav.link('Patient/Suivi', {id: patient.id});

            return html`
                <tr>
                    <td>${patient.values.id}</td>
                    <td>${patient.values.nom}</td>
                    <td>${patient.values.prenom}</td>
                    <td style="text-align: right;"><a href=${href}>Afficher</a></td>
                </tr>
            `;
        })
    } else {
        return html`<tr><td colspan="4">Aucun patient présent</td></tr>`;
    }
}

function createPatient(e) {
    goupile.popup(e, 'Créer', (page, close) => {
        let id = page.text('*id', 'Identifiant')
        page.submitHandler = () => {
            close();

            route.ctx = id.value;
            go("Patient")
        }
    })
}
