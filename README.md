# Anabiosis
Resurrecting the classic Windows taskbar experience. 

Anabiōnontas tēn klasikēn empeirian tēs grammēs ergasiōn tōn Windows.

## 🛠️ Roadmap & Upcoming Features 
This project is currently in active development to faithfully recreate the Windows Vista Start experience on Windows 11. Below are the primary objectives for the next development cycle:

### 1. Direct2D Rendering Engine Integration

**Objective:** Transition the Start Orb rendering from GDI/GDI+ (used for testing) to Direct2D for authentic Aero effects.

**Key Tasks:**

    Implement hardware-accelerated drawing to recreate the iconic Vista "Glass" pulse animations and high-DPI support.

    Handle device-dependent resource creation to ensure stable rendering of the circular Start Orb.

    Apply advanced anti-aliasing and layer blending to achieve the specific semi-transparent "Aero Glass" aesthetic.

### 2. Refinement of Start Button Interaction Logic

**Objective:** Restore the classic hover and click behaviors unique to the Vista-style button.

**Key Tasks:**

    Optimize message hook handling (WndProc) to manage the Orb's idle, hover, and glowing active states.

    Improve hit-testing logic specifically for the circular Orb geometry on the Windows 11 taskbar.

    Ensure seamless integration with Windows Shell to override the default Start button behavior.

### 3. Start Menu Implementation & Processing

**Objective:** Develop the core Vista-style dual-pane Start Menu and system integration.

**Key Tasks:**

    Architect the classic two-pane layout engine, including the integrated search bar and "All Programs" navigation.

    Handle shell execution for launching applications and system commands directly from the custom menu.

    Implement DWM-based transparency and blur effects to match the original Windows Vista visual style.
