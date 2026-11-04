// sverity-args: --top top --sdc @DIR@/boundary_derived_constant.sdc
// expect: CST-014 WARNING
// expect-none: CDC-003
module top(input logic a, b, mode, en, output logic q);
  wire disabled = ~mode;
  wire enable_crossing = disabled && en;
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (enable_crossing) q <= source_q;
endmodule
