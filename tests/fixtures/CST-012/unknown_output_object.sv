// sverity-args: --top top --sdc @DIR@/unknown_output_object.sdc
// expect-none: CST-009
// expect-none: CST-012
// expect: CST-018 WARNING
// expect-coverage: constraints INCOMPLETE
module top(input logic clk, output logic q);
  always_ff @(posedge clk) q <= 1'b1;
endmodule
