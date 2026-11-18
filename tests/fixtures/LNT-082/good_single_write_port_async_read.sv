// sverity-args: --top top
// One nonblocking write port and an asynchronous read: defined in simulation, so no collision obligation; the distributed-RAM advice stays.
// expect: LNT-082 INFO 8
// expect-none: LNT-082 WARNING
module top(input logic clk, we, input logic [3:0] wa, ra, input logic [7:0] wd, output logic [7:0] rd);
  logic [7:0] rf [16];
  always_ff @(posedge clk) if (we) rf[wa] <= wd;
  assign rd = rf[ra];
endmodule
