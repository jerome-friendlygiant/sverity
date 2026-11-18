// sverity-args: --top top
// One process writes rf[addr], which may reach word 3, and another writes rf[3]: word 3 may have two write processes.
// expect: LNT-049 WARNING 5
module top(input logic clk, we, input logic [1:0] addr, input logic [1:0] d, output logic [1:0] q);
  logic [1:0] rf [4];
  always_ff @(posedge clk) if (we) rf[addr] <= d;
  always_ff @(posedge clk) if (we) rf[3] <= ~d;
  assign q = rf[0];
endmodule
