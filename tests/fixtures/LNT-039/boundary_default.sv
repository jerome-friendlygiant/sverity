// sverity-args: --top top
// expect: LNT-039 WARNING 8
// A default branch does not establish that its X value is unreachable or intentional.
module top(input logic [1:0] s, input logic a, output logic y);
  always_comb begin
    case (s)
      2'b00: y = a;
      default: y = 'x;
    endcase
  end
endmodule
