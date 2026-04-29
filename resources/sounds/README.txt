This directory contains application audio assets for parking gate announcements.

Expected files:
  open_entry.wav       — Announcement when the entry gate opens
  open_exit.wav        — Announcement when the exit gate opens
  payment_success.wav  — Confirmation of successful payment
  monthly_valid.wav    — Monthly pass validation success
  insert_money.wav     — Instruction to insert money
  thank_you.wav        — Thank-you message
  error.wav            — Error notification

Recommended audio format:
  - 16-bit PCM WAV
  - 44.1 kHz sample rate
  - mono or stereo
  - Vietnamese voice content for this deployment

Generated files in this folder are used by the app at runtime.
If the app process runs from the project root, it will resolve:
  resources/sounds/<file>.wav

Generation example (using espeak + sox):
  espeak -v vi -s 120 -a 140 "Xin mời vào, cổng ra sẽ mở" --stdout \
    | sox -t wav - -r 44100 -b 16 resources/sounds/open_entry.wav

Testing playback:
  # Use a command-line player if available
  aplay resources/sounds/open_entry.wav
  # or
  ffplay -autoexit resources/sounds/open_entry.wav

If you need to regenerate files, use the same command pattern and adjust text for each key.
For best results, keep phrases clear, slow, and concise so the audio is easy to understand.
