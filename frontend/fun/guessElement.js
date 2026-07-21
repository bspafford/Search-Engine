function lerp(a, b, t) {
    return a + (b - a) * t;
}

function lerpColor(color1, color2, t) {
    const r = Math.round(lerp(color1.r, color2.r, t));
    const g = Math.round(lerp(color1.g, color2.g, t));
    const b = Math.round(lerp(color1.b, color2.b, t));

    return `rgb(${r}, ${g}, ${b})`;
}

class GuessCard extends HTMLElement {
    connectedCallback() {
        const rank = this.getAttribute("rank");
        const guess = this.getAttribute("guess");
        const score = parseFloat(this.getAttribute("score"));

        const percent = Math.exp(-0.001535 * score);

        const start = { r: 255, g: 0, b: 0 };
        const end   = { r: 0, g: 255, b: 0 };

        const color = lerpColor(start, end, percent);
        const width = `${percent * 100}%`;
        console.log(score + ", " + percent);

        this.innerHTML = `
            <div class="guessBox">
                <div class="backgroundScore" style="background-color: ${color}; width: ${width}"></div>
                <div class="guessData">
                    <div>
                        <p>#${rank}</p>
                        <p>${guess}</p>
                    </div>
                    <p>${Math.round(score)}</p>
                </div>
            </div>
        `;
    }
}

customElements.define("guess-card", GuessCard);
