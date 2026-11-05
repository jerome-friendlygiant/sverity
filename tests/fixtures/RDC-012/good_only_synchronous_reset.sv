// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// No asynchronous reset assertion source exists: the power-intent obligation trigger is absent.
// expect-none: RDC-012
module top(input logic clka, rst, d, output logic q);
  always_ff @(posedge clka) if (rst) q <= 1'b0; else q <= d;
endmodule
