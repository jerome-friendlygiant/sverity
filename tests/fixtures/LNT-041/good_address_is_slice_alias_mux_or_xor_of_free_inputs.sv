// sverity-args: --top top
// Every bit of each address is a distinct bit of a free top-level input (a slice, a plain alias net, a mux of free inputs, an XOR with a
// constant), so each takes every value of its width and the 8 words are all written.
// expect-none: LNT-041
module child(input logic clk, we, input logic [2:0] wa, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  logic [7:0] mem [8];
  always_ff @(posedge clk) if (we) mem[wa] <= wd;
  assign rd = mem[ra];
endmodule
module top(input logic clk, we, s, input logic [31:0] addr, input logic [2:0] a1, a2, input logic [7:0] wd, input logic [2:0] ra,
           output logic [7:0] r0, r1, r2, r3);
  wire [2:0] w = a1;
  child u0(.clk, .we, .wa(addr[4:2]), .wd, .ra, .rd(r0));
  child u1(.clk, .we, .wa(w), .wd, .ra, .rd(r1));
  child u2(.clk, .we, .wa(s ? a1 : a2), .wd, .ra, .rd(r2));
  child u3(.clk, .we, .wa(a2 ^ 3'd5), .wd, .ra, .rd(r3));
endmodule
