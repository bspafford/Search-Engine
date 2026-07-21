const guessForm = document.getElementById("guessForm");
const guessInput = document.getElementById("guessInput");

async function getData() {
    console.log("submitting value: " + guessInput.value);
    const response = await fetch("/guess", {
        method: "POST",
        headers: {
            "Content-Type": "application/json"
        },
        body: guessInput.value
    });

    const result = await response.text();
    console.log(result);
}

guessForm.addEventListener('submit', function(e) {
    e.preventDefault();
    getData();
});
