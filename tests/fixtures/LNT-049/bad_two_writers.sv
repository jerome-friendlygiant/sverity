// sverity-args: --top top
// expect: LNT-049 WARNING
module top(input logic clk, we, input logic [1:0] a, b, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] mem [4];
  always_ff @(posedge clk) if (we) mem[a] <= d;
  always_ff @(posedge clk) if (we) mem[b] <= ~d;
  assign q = mem[a];
endmodule
