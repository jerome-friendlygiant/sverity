// sverity-args: --top top
// A `.*` pattern matches every value, so the item after it is never reached.
// expect: LNT-064 WARNING 8
module top(input logic [3:0] s, output logic y);
  always_comb begin
    y = 1'b0;
    case (s) matches
      .*: y = 1'b1;
      4'b0001: y = 1'b0;
    endcase
  end
endmodule
