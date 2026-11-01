// sverity-args: --top top
// partial wildcards are the normal casez idiom
// expect-none: LNT-064
module top(input logic [1:0] s, output logic [1:0] y);
  always_comb begin
    case (s) inside
      2'b1?: y = 1;
      default: y = 0;
    endcase
    casez (s)
      2'b1?: y = 2;
      default: y = 3;
    endcase
  end
endmodule
