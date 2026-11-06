// sverity-args: --top top
// The analysed top itself is attributed as a black box: its body is a stand-in, so nothing in it is evidence.
// expect: CST-005 WARNING 5
// expect-none: CDC-003 INFO
(* black_box *) module top(input logic clk, d, output logic q);
  logic s1;
  always_ff @(posedge clk) begin s1 <= d; q <= s1; end
endmodule
