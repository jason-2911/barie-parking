Place WAV files here:
  open_entry.wav       — Cần vào mở
  open_exit.wav        — Cần ra mở
  payment_success.wav  — Thanh toán thành công
  monthly_valid.wav    — Vé tháng hợp lệ
  insert_money.wav     — Hướng dẫn nạp tiền
  thank_you.wav        — Cảm ơn
  error.wav            — Lỗi

Recommended: 16-bit PCM WAV, 44100Hz, mono/stereo
Generate with espeak:
  espeak -v vi "Xin mời vào" -w open_entry.wav
  espeak -v vi "Cảm ơn quý khách" -w thank_you.wav
