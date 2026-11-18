// sverity-args: --top top
// Constants with no X or Z bit, and a select of the known bits of one that has them, compare as plain values.
// expect-none: LNT-040
typedef enum logic [1:0] {A = 2'b00, B = 2'b01} e_t;
module top(input e_t s, input logic [1:0] a, output logic y);
  localparam logic [1:0] P = 2'b01;
  localparam logic [1:0] PX = 2'bx1;
  always_comb begin
    y = 1'b0;
    case (s)
      A: y = 1'b1;
      B: y = (a == P) || (a inside {P, 2'b10}) || (PX[0] == a[0]);
      default: y = 1'b0;
    endcase
    if (P[0]) y = 1'b1;
  end
endmodule
