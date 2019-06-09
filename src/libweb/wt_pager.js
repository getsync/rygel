// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

let wt_pager = (function() {
    function Pager()
    {
        let self = this;

        this.hrefBuilder = page => '#';
        this.changeHandler = null;

        let root_el;

        let last_page;
        let current_page;

        this.getRootElement = function() { return root_el; }

        this.setLastPage = function(page) { last_page = page; }
        this.getLastPage = function() { return last_page; }
        this.setCurrentPage = function(page) { current_page = page; }
        this.getCurrentPage = function() { return current_page; }

        function handlePageClick(e, page)
        {
            current_page = page;
            setTimeout(() => self.render(root_el), 0);

            if (self.changeHandler)
                self.changeHandler.call(self, e);

            e.preventDefault();
        }

        function makePageLink(text, page)
        {
            if (page) {
                return html`<td><a href=${self.hrefBuilder(page)} @click=${e => handlePageClick(e, page)}>${text}</a></td>`;
            } else {
                return html`<td>${text}</td>`;
            }
        }

        this.render = function(new_root_el) {
            root_el = new_root_el;

            let start_page, end_page;
            if (last_page < 8) {
                start_page = 1;
                end_page = last_page;
            } else if (current_page < 5) {
                start_page = 1;
                end_page = 5;
            } else if (current_page > last_page - 4) {
                start_page = last_page - 4;
                end_page = last_page;
            } else {
                start_page = current_page - 1;
                end_page = current_page + 1;
            }

            render(html`
                <table class="pagr">
                    ${makePageLink('≪', current_page > 1 ? (current_page - 1) : null)}
                    ${start_page > 1 ?
                        html`${makePageLink(1, 1)}<td> … </td>` : html``}
                    ${util.mapRange(start_page, end_page + 1,
                                    page => makePageLink(page, page !== current_page ? page : null))}
                    ${end_page < last_page ?
                        html`<td> … </td>${makePageLink(last_page, last_page)}` : html``}
                    ${makePageLink('≫', current_page < last_page ? (current_page + 1) : null)}
                </table>
            `, root_el);
        }
    }

    this.create = function() { return new Pager(); };

    this.computeLastPage = function(render_count, row_count, page_length) {
        let last_page = Math.floor((row_count - 1) / page_length + 1);
        if (last_page === 1 && render_count === row_count)
            last_page = null;

        return last_page;
    };

    return this;
}).call({});
