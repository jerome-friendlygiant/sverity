// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Every packed state bit has the same asynchronous reset.
// expect-none: RDC-020
module top(input logic clka, rst_n, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] v;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) v <= '0; else v <= d;
  assign q = v;
endmodule
