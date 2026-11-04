// sverity-args: --top top --sdc @DIR@/good_no_mode.sdc
// expect-none: CST-014
module top(input logic a, b, output logic q);
  always_ff @(posedge b) q <= 1'b0;
endmodule
