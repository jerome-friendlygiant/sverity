// sverity-args: --top top
// `we && EN` with EN = 0 can never be true: the only write builds no hardware, so the memory is never driven.
// expect: LNT-008 WARNING 5
module top #(parameter bit EN = 0)(input logic clk, we, input logic [2:0] wa, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  logic [7:0] mem [8];
  always_ff @(posedge clk) if (we && EN) mem[wa] <= wd;
  assign rd = mem[ra];
endmodule
