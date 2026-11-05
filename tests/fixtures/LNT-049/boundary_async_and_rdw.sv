// sverity-args: --top top
// A combinational read of a memory and a read-during-write in one clocked process are reported as INFO (target-dependent semantics).
// expect: LNT-082 INFO
module top(input logic clk, we, input logic [1:0] a, input logic [3:0] d, output logic [3:0] q, r);
  logic [3:0] mem [4];
  always_ff @(posedge clk) begin
    if (we) mem[a] <= d;
    r <= mem[a];
  end
  assign q = mem[a];
endmodule
