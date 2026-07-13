const searchForm = document.getElementById("searchForm");
searchForm.addEventListener("submit", async function(event) {
    event.preventDefault();

    const query = document.getElementById("searchInput").value;

    const response = await fetch('/api/search', {
        method: "POST",
        headers: {
            "Content-Type": "application/json"
        },
        body: JSON.stringify({
            query: query
        })
    });

    const data = await response.json();
    console.log(data);
})
