// sverity-args: --top top --option LNT-006.dynamic_index=true
// Every value of each index selects an element: a full-width index, a narrower one, and a loop bounded by the array.
// expect-none: LNT-006
module top(input logic clk, input logic [2:0] a3, input logic [1:0] a2, input logic [7:0] d, output logic [7:0] x, y, z);
  logic [7:0] mem [0:7];
  localparam logic [5:0][7:0] TABLE = '0;
  always_ff @(posedge clk) mem[a3] <= d;
  assign x = mem[a3];
  assign y = TABLE[a2];
  always_comb begin
    z = '0;
    for (int i = 0; i < 6; i++) z ^= TABLE[i];
  end
endmodule
