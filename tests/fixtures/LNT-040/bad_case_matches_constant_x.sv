// sverity-args: --top top
// A plain `case ... matches` constant pattern with an X bit behaves like the case item: it never matches.
// expect: LNT-040 WARNING 7
module top(input logic [3:0] s, output logic y);
  always_comb begin
    case (s) matches
      4'b00x1: y = 1'b1;
      default: y = 1'b0;
    endcase
  end
endmodule
