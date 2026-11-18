// sverity-args: --top top
// An X in a range bound of a case inside item is a relational comparison, not a wildcard.
// expect: LNT-040 WARNING 8
module top(input logic [3:0] a, output logic y);
  always_comb begin
    y = 1'b0;
    case (a) inside
      [4'bx001 : 4'd9]: y = 1'b1;
      default: ;
    endcase
  end
endmodule
