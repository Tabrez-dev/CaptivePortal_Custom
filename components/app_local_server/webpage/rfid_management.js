/**
 * RFID Card Management JavaScript
 * Handles the client-side functionality for the RFID card management interface
 */

// Global variables
var cardList = [];

/**
 * Initialize functions when the document is ready
 */
$(document).ready(function() {
  // Load initial data
  getCardCount();
  loadCardList();

  // Set up event handlers
  $("#addCardForm").on("submit", function(e) {
    e.preventDefault();
    addCard();
  });

  $("#checkCardForm").on("submit", function(e) {
    e.preventDefault();
    checkCard();
  });

  $("#resetDatabaseBtn").on("click", function() {
    showResetConfirmation();
  });

  $("#cancelResetBtn").on("click", function() {
    hideResetConfirmation();
  });

  $("#confirmResetBtn").on("click", function() {
    resetDatabase();
    hideResetConfirmation();
  });
});

/**
 * Gets the total count of RFID cards in the database
 */
function getCardCount() {
  $.getJSON('/cards/Count', function(data) {
    $("#cardCount").text(data.count);
    
    // Show/hide the no cards message based on count
    if (data.count === 0) {
      $("#cardTableContainer").hide();
      $("#noCardsMessage").show();
    } else {
      $("#cardTableContainer").show();
      $("#noCardsMessage").hide();
    }
  }).fail(function(jqXHR, textStatus, errorThrown) {
    console.error("Failed to get card count: " + textStatus + ", " + errorThrown);
  });
}

/**
 * Loads the list of RFID cards from the server
 */
function loadCardList() {
  $.getJSON('/cards/Get', function(data) {
    if (data && data.cards) {
      cardList = data.cards;
      renderCardTable();
    } else {
      console.error("Unexpected response format from /cards/Get");
      $("#cardTableBody").html("<tr><td colspan='3'>Error loading card data</td></tr>");
    }
  }).fail(function(jqXHR, textStatus, errorThrown) {
    console.error("Failed to load card list: " + textStatus + ", " + errorThrown);
    $("#cardTableBody").html("<tr><td colspan='3'>Error loading card data: " + textStatus + "</td></tr>");
  });
}

/**
 * Renders the card table with the current card list data
 */
function renderCardTable() {
  var tableBody = $("#cardTableBody");
  tableBody.empty();
  
  if (cardList.length === 0) {
    $("#cardTableContainer").hide();
    $("#noCardsMessage").show();
    return;
  }
  
  $("#cardTableContainer").show();
  $("#noCardsMessage").hide();
  
  cardList.forEach(function(card) {
    var row = $("<tr>");
    
    // Display card ID in decimal format
    var cardIdDecimal = parseInt(card.id).toString(10);
    
    row.append($("<td>").text(cardIdDecimal));
    row.append($("<td>").text(card.nm));
    
    var actionsCell = $("<td>");
    var deleteButton = $("<button>")
      .addClass("delete-btn")
      .text("Delete")
      .on("click", function() {
        deleteCard(card.id);
      });
    
    actionsCell.append(deleteButton);
    row.append(actionsCell);
    
    tableBody.append(row);
  });
}

/**
 * Adds a new RFID card to the database
 */
function addCard() {
  var cardId = $("#cardId").val().trim();
  var cardName = $("#cardName").val().trim();
  
  // Basic validation
  if (!cardId || !cardName) {
    showAddCardMessage("Please fill in all fields", "error");
    return;
  }
  
  // Convert hex string to number if needed
  var numericCardId;
  if (cardId.toLowerCase().startsWith("0x")) {
    numericCardId = parseInt(cardId, 16);
  } else {
    numericCardId = parseInt(cardId, 10);
  }
  
  // Check if the conversion was successful
  if (isNaN(numericCardId)) {
    showAddCardMessage("Invalid card ID format", "error");
    return;
  }
  
  // Prepare the data
  var cardData = {
    id: numericCardId,
    nm: cardName
  };
  
  // Send the request
  $.ajax({
    url: '/cards/Add',
    type: 'POST',
    contentType: 'application/json',
    data: JSON.stringify(cardData),
    success: function(response) {
      showAddCardMessage("Card added successfully", "success");
      $("#cardId").val("");
      $("#cardName").val("");
      
      // Refresh the card list and count
      getCardCount();
      loadCardList();
    },
    error: function(xhr, status, error) {
      var errorMsg = "Failed to add card";
      
      // Try to extract more specific error message if available
      try {
        var response = JSON.parse(xhr.responseText);
        if (response && response.message) {
          errorMsg = response.message;
        }
      } catch (e) {
        // If parsing fails, use status text
        errorMsg += ": " + xhr.statusText;
      }
      
      showAddCardMessage(errorMsg, "error");
    }
  });
}

/**
 * Deletes an RFID card from the database
 */
function deleteCard(cardId) {
  if (!confirm("Are you sure you want to delete this card?")) {
    return;
  }
  
  $.ajax({
    url: '/cards/Delete?id=' + cardId,
    type: 'DELETE',
    success: function(response) {
      // Refresh the card list and count
      getCardCount();
      loadCardList();
    },
    error: function(xhr, status, error) {
      alert("Failed to delete card: " + xhr.statusText);
    }
  });
}

/**
 * Checks if a card exists in the database
 */
function checkCard() {
  var cardId = $("#checkCardId").val().trim();
  
  // Basic validation
  if (!cardId) {
    showCheckCardMessage("Please enter a card ID", "error");
    return;
  }
  
  // Prepare the data - convert to string for consistency with backend
  var cardData = {
    card_id: cardId
  };
  
  // Send the request
  $.ajax({
    url: '/cards/Check',
    type: 'POST',
    contentType: 'application/json',
    data: JSON.stringify(cardData),
    success: function(response) {
      // Use the card ID as is (already in hex format from server)
      var cardIdDisplay = response.card_id;
      
      if (response.exists) {
        showCheckCardMessage("Card " + cardIdDisplay + " exists in the database", "success");
      } else {
        showCheckCardMessage("Card " + cardIdDisplay + " does NOT exist in the database", "error");
      }
    },
    error: function(xhr, status, error) {
      showCheckCardMessage("Error checking card: " + xhr.statusText, "error");
    }
  });
}

/**
 * Resets the RFID database to default values
 */
function resetDatabase() {
  $.ajax({
    url: '/cards/Reset',
    type: 'POST',
    success: function(response) {
      alert("RFID database has been reset successfully");
      
      // Refresh the card list and count
      getCardCount();
      loadCardList();
    },
    error: function(xhr, status, error) {
      alert("Failed to reset database: " + xhr.statusText);
    }
  });
}

/**
 * Shows the reset confirmation modal
 */
function showResetConfirmation() {
  $("#confirmResetModal").css("display", "block");
}

/**
 * Hides the reset confirmation modal
 */
function hideResetConfirmation() {
  $("#confirmResetModal").css("display", "none");
}

/**
 * Shows a message in the add card form
 */
function showAddCardMessage(message, type) {
  var messageElement = $("#addCardMessage");
  messageElement.text(message);
  
  // Clear existing classes
  messageElement.removeClass("error-message success-message");
  
  // Add appropriate class
  if (type === "error") {
    messageElement.addClass("error-message");
  } else if (type === "success") {
    messageElement.addClass("success-message");
  }
}

/**
 * Shows a message in the check card form
 */
function showCheckCardMessage(message, type) {
  var messageElement = $("#checkCardMessage");
  messageElement.text(message);
  
  // Clear existing classes
  messageElement.removeClass("error-message success-message");
  
  // Add appropriate class
  if (type === "error") {
    messageElement.addClass("error-message");
  } else if (type === "success") {
    messageElement.addClass("success-message");
  }
}
