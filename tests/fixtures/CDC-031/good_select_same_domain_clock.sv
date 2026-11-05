// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Near miss of bad_select_not_synchronous.sv: the select is a register in clka and the mux switches clka and its inverse, the same clock.
// expect-none: CDC-031
module top(input logic clka, clkb, sel, rst_n, d, output logic q);
  logic mclk, selr;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) selr <= 1'b0; else selr <= sel;
  assign mclk = selr ? clka : ~clka;
  always_ff @(posedge mclk or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
