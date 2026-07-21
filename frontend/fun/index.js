const guessContainer = document.getElementById("guessContainer");
const guessForm = document.getElementById("guessForm");
const guessInput = document.getElementById("guessInput");

let guessList = [];

async function getData() {
    console.log("submitting value: " + guessInput.value);
    const response = await fetch("/guess", {
        method: "POST",
        headers: {
            "Content-Type": "application/json"
        },
        body: guessInput.value
    });

    const result = await response.json();
    console.log(result);

    if (result["correct"] == true) {
        console.log("You won!");
        document.getElementById("winScreen").style.display = "flex";
        return;
    }

    guessList.push({ score: parseFloat(result["score"]), name: guessInput.value });
    guessList.sort((a, b) => a.score - b.score);
    guessContainer.innerHTML = "";
    for (let i = 0; i < guessList.length; ++i) {
        const guess = document.createElement("guess-card");
        guess.setAttribute("rank", i + 1);
        guess.setAttribute("guess", guessList[i].name);
        guess.setAttribute("score", guessList[i].score);
        guessContainer.appendChild(guess);
    }

    guessInput.value = "";
    guessInput.focus();
}

guessForm.addEventListener('submit', function(e) {
    e.preventDefault();
    getData();
});
