class SearchItem extends HTMLElement {
    connectedCallback() {
        const url = this.getAttribute("url") || "";
        const title = this.getAttribute("title") || "";
        const description = this.getAttribute("description") || "";
        const favicon = this.getAttribute("favicon") || "";

        this.innerHTML = `
            <div class="searchItem">
                <a class="pageLinkBox" href="${url}">
                    <img class="favIcon" src="favicons/${favicon}">
                    <div>
                        <p class="pageTitle">${title}</p>
                        <p class="pageLink">${url}</p>
                    </div>
                </a>
                <a class="pageLinkTitle" href="${url}">${title}</a>
                <p class="pageDescription">${description}</p>
            </div>
        `;
    }
}

customElements.define("search-item", SearchItem);
