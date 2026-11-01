// sverity-args: --top top
// expect: LNT-039 INFO 8
// expect-none: LNT-039 WARNING
module top(input logic [1:0] s, input logic a, output logic y);
  always_comb begin
    case (s)
      2'b00: y = a;
      default: y = 'x;
    endcase
  end
endmodule
