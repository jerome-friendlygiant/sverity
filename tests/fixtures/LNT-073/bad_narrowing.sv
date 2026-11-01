// sverity-args: --top top
// expect: LNT-073 WARNING 5
// expect-count: LNT-073 WARNING 1
module top(output logic [3:0] lo, output logic [7:0] ok);
  assign lo = 8'd200;    // 200 changes value in 4 bits (stored as 8)
  assign ok = 8'd200;    // fits: no finding
endmodule
