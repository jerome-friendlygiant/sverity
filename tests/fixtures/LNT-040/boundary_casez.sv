// sverity-args: --top top
// expect-none: LNT-040
module top(input logic [1:0] a, output logic m);
  always_comb begin
    m = 1'b0;
    casez (a)
      2'b1?: m = 1'b1;
      default: m = 1'b0;
    endcase
  end
endmodule
