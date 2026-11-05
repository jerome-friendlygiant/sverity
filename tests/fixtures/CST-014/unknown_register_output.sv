// sverity-args: --top top --sdc @DIR@/unknown_register_output.sdc
// A register output changes over time, and nothing shows it stays at the requested value.
// expect: CST-009 WARNING
// expect-none: CST-014
// expect: CDC-028 ERROR
module top(input logic a, b, en, output logic q);
  logic m;
  always_ff @(posedge b) m <= en;
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (m) q <= source_q;
endmodule
