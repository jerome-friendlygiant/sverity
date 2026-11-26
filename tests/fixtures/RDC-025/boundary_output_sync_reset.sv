// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The output register has only a synchronous reset, which is a reset candidate, so the output is exempt: no asynchronous reset is required.
// expect-none: RDC-025
module top(input logic clka, rst_n, d, output logic q);
  always_ff @(posedge clka) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
