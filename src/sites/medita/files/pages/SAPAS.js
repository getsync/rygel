data.makeFormHeader("Echelle SAPAS (Standardised Assessment of Personality – Abbreviated Scale)", page)
form.output(html`
    <p>Cochez Oui (ou Non pour la question 3) si le patient pense que la description s’applique la plupart du temps et dans la plupart des situation.</p>
`)

form.pushOptions({mandatory: true, missingMode: 'disable'})

form.binary("a", "1. En général, avez-vous des difficultés à vous faire et à garder des amis ?")
form.binary("b", "2. Vous décririez vous comme un(e) solitaire ?")
form.binary("c", "3. En général, faites-vous confiance aux autres ?")
form.binary("d", "4. Habituellement, perdez-vous votre sang froid facilement?")
form.binary("e", "5. Êtes-vous habituellement, une personne impulsive ?")
form.binary("f", "6. Êtes-vous habituellement une personne inquiète ?")
form.binary("g", "7. En général, dépendez-vous beaucoup des autres ?")
form.binary("h", "8. En général, êtes-vous perfectionniste ? ")


let score = form.value("a") +
            form.value("b") +
            form.value("c") +
            form.value("d") +
            form.value("e") +
            form.value("f") +
            form.value("g") +
            form.value("h")
form.calc("score", "Score total", score)

data.makeFormFooter(nav, page)