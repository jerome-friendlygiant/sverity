// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect-none: RDC-009
module top(input logic clka, rst_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
