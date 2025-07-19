
# AI Desktop Tool Project Documentation

## 1. Project Overview

This document details the development of a desktop-based Artificial Intelligence (AI) tool built with Qt. The tool currently focuses on text-based AI interactions, offering conversation management, context-aware responses, and basic text rewriting capabilities. The project aims to evolve into a versatile MVP (Minimum Viable Product) with advanced features like clipboard integration for intelligent suggestions, image generation, and a refined user interface.

## 2. Usage Instructions

### 2.1. Development Environment
- **Qt Version**: 6.9.1
- **Compiler**: MSVC x64
- **Build System**: CMake

### 2.2. How to Open and Build the Project
1. Open Qt Creator.
2. Select "Open Project" and navigate to your project directory to select the `CMakeLists.txt` file.
3. Build the project in Qt Creator.

### 2.3. API Key Configuration
For security reasons, set the API key manually in `geminiclient.cpp`:

```cpp
aiClient->setApiKey("YOUR_GEMINI_API_KEY_HERE");
```

### 2.4. Running the Application
- After building, run the application and ensure `chathistory.json` is in the same directory as the executable.

## 3. Technical Thought Process: AI Class Functionality Implementation

### 3.1. AIClient: Defining a Common Interface for AI Services
The core of the AI tool lies in the `AIClient` and `GeminiClient` classes. These classes work together to manage communication with the AI service.

#### 3.1.1. AIClient Overview
`AIClient` is an abstract base class that defines the general interface for all AI clients. It ensures that no matter which AI model you integrate (such as Gemini or another one), they will follow the same interface, making the code more extensible and maintainable.

- **Purpose**: 
  - **Define General APIs**: Methods like `setApiKey()`, `sendMessage()`, `requestConversationTitle()`, and `sendRewriteRequest()` are defined as virtual functions that any AI client must implement.
  - **Define General Signals**: Signals such as `aiResponseReceived()`, `errorOccured()`, `titleGenerated()`, and `rewritedContentReceived()` are used to notify the external application (like `MainWindow`) about AI responses, errors, or specific request results.
  - **Network Management**: The class contains a `QNetworkAccessManager* networkManager` member for handling all network requests.

#### 3.1.2. GeminiClient: Concrete Implementation for the Gemini API
`GeminiClient` inherits from `AIClient` and implements the specific logic for interacting with the Google Gemini API. It serves as the "bridge" between your application and the Gemini model.

##### 3.1.2.1. Key Implementation Mechanisms:

a. **Request Type Enumeration**: 
In order to differentiate between request types (such as chat message, title generation, or rewrite request), `GeminiClient` defines an enum:

```cpp
enum class RequestType {
    ChatMessage,      // Chat message request
    TitleGeneration,   // Title generation request
    Rewrite           // Rewrite request
};
```

When you call methods like `sendMessage()`, `requestConversationTitle()`, or `sendRewriteRequest()`, this enum marks the type of request.

b. **Mapping `QNetworkReply*` to `RequestType`**: 
Network requests are asynchronous. When you send a `QNetworkReply* reply = networkManager->post(request, data);`, the response is not immediate. To track which request corresponds to which response, `GeminiClient` uses a `QMap`:

```cpp
QMap<QNetworkReply*, RequestType> requestTypeMap;
```

c. **Unified Handling of All Network Replies**: 
All `QNetworkAccessManager` `finished` signals are connected to `GeminiClient::onNetworkReplyFinished`, ensuring all AI service network replies are processed in a single function.

- **Process Flow**:
    1. **Send Request**: When you send a request via `networkManager->post()`, the corresponding `RequestType` is added to `requestTypeMap`.
    2. **Handle Response**: Upon receiving a response in `onNetworkReplyFinished(QNetworkReply *reply)`, the type of request is looked up from `requestTypeMap`. Then, based on the request type, the relevant signal is emitted (e.g., `aiResponseReceived()`, `titleGenerated()`, `rewritedContentReceived()`).
    3. **Resource Cleanup**: Once the request is handled, `reply->deleteLater()` is called to safely delete the `QNetworkReply` object.

This design allows for clear request management, centralized response handling, and easy extensibility.

### 3.2. Benefits of This Approach
- **Clear Request Management**: Differentiating request types through enums and a mapping system ensures each request is tracked and handled properly.
- **Centralized Response Handling**: All network responses are processed in one place, simplifying code structure.
- **Scalability**: New AI request types can be easily added by modifying the enum and extending `onNetworkReplyFinished()` without creating new functions for each request type.

## 4. Current Features (MVP - Phase 1)

### 4.1. AI API Integration
- Connects to an external AI service (specifically, Gemini 2.5 Pro via `llmxapi.com`) to send user queries and receive AI responses.
- Manages API key for authentication.
- **API URL Modification**: The URL used for API communication should be updated in the `GeminiClient` class itself. The URL can be modified as needed to connect to the appropriate model endpoint:
  ```cpp
  QUrl url("https://generativelanguage.googleapis.com/v1beta/models/gemini-pro:generateContent");
  ```

### 4.2. Conversation Management
- **Local Chat History Saving**: Conversations are saved locally in a `chathistory.json` file, ensuring persistence across application sessions.
- **Conversation Loading**: Upon startup, the application loads previous conversations from `chathistory.json` and displays them in a list view.
- **New Chat Initiation**: Users can start new conversations, clearing the current chat view and preparing for a fresh interaction.
- **Conversation Selection**: Users can select and review past conversations from the chat history list.
- **Dynamic Conversation Titles**: The AI can automatically generate concise and descriptive titles for new conversations based on the first user message.

### 4.3. Context-Aware Responses
- The AI client is designed to send the entire conversation history along with the new user input, enabling the AI to provide contextually relevant responses. This is managed by the `builtGeminiContents` function, which structures the conversation for the Gemini API.

### 4.4. Text Rewriting Functionality
- Users can send requests to the AI to rewrite specified text, with an option to include specific demands for the rewriting style. This functionality is exposed through the `sendRewriteRequest` method.
- **Potential Market**: The rewrite functionality has significant potential for market growth, particularly when integrated into tools like Chrome or Outlook. This would provide automatic text detection and rewriting, offering considerable help to those who frequently engage in email communications, making their writing process more efficient and polished.

### 4.5. User Interface (UI)
- Basic Qt-based UI with a chat history list view and a main display area for conversation.
- Dynamically adjusts chat text color based on the system's theme (light/dark mode) for better readability.
- Provides visual feedback during AI processing by disabling the send button.
- Error handling for network issues and AI response parsing, displaying user-friendly messages.

## 5. Gemini API Request Structure

The GeminiClient class constructs the JSON payload sent to the Gemini API based on different request types.

### 5.1. Chat Message (sendMessage)

This is the JSON format sent to the API when requesting a conversation response:

```json
{
  "contents": [
    {
      "role": "user",
      "parts": [
        {
          "text": "User historical message 1"
        }
      ]
    },
    {
      "role": "model",
      "parts": [
        {
          "text": "AI historical response 1"
        }
      ]
    },
    {
      "role": "user",
      "parts": [
        {
          "text": "Current user input"
        }
      ]
    }
  ],
  "generationConfig": {
    "temperature": 0.3 // Adjusts randomness of the generated response
  }
}
```

### 5.2. API Response Format

The API response comes in this format, which contains the generated AI response:

```json
{
  "candidates": [
    {
      "content": {
        "role": "model", // Typically "model" as the AI generates the response
        "parts": [
          {
            "text": "This is the AI-generated response text"
          }
        ]
      },
      "finishReason": "STOP", // Reason the generation stopped (e.g., "STOP" when done)
      "index": 0 // Index of the response in case there are multiple options
    }
  ]
}
```

## 6. Known Limitations

- **Windows API for Auto Text Detection**: Currently, the tool requires manual copying of text for rewrite functionality. Using Windows API to automatically detect text fields would streamline the process.
- **Asynchronous Processing**: To improve responsiveness, asynchronous handling of new chats could be implemented where the title is retrieved first, followed by the conversation content.

## 7. Future Enhancements

- **Clipboard Detection for Auto-Rewrite Suggestions**: Potentially integrate this tool into applications like Chrome or Outlook for automatic text detection and rewriting. This can be highly beneficial for users who often communicate via email.
- **Image Generation Integration**
- **UI/UX Improvements**
- **Audio Recognition**
- **User Feedback and Iteration**
- **Text Formatting Beautification**: Enhance the formatting of the AI's responses, which are currently raw text, by improving visual presentation for readability.

## 8. Conclusion

The AI Desktop Tool has a solid foundation with essential AI interaction features. The planned enhancements will significantly expand its utility, transforming it into a more powerful and user-friendly MVP.
