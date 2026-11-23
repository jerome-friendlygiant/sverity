// sverity-args: --top top
// expect-none: CDC-009
module top(input logic clka, input logic [1:0] d, output logic [1:0] q);
  always_ff @(posedge clka) q <= d;
endmodule
