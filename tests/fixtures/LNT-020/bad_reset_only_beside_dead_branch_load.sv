// sverity-args: --top top
// The only write outside the reset branch sits under `if (P)` with P = 0: it never runs, so `s` never loads.
// expect: LNT-020 WARNING 6
module top #(parameter bit P = 0) (input logic clk, rst, d, output logic s);
  always_ff @(posedge clk) begin
    if (rst) s <= 1'b0;
    else if (P) s <= d;
  end
endmodule
