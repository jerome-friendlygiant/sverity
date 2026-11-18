// sverity-args: --top top
// A plain case item that is an enum value holding X bits behaves like the literal 2'bx1: it never matches.
// expect: LNT-040 WARNING 10
typedef enum logic [1:0] {A = 2'b00, DC = 2'bx1} e_t;
module top(input e_t s, output logic y);
  always_comb begin
    y = 1'b0;
    case (s)
      A: y = 1'b1;
      DC: y = 1'b1;
      default: y = 1'b0;
    endcase
  end
endmodule
