// sverity-args: --top top
// a timing control between two assignments lets other processes observe the first value: no finding
// expect-none: LNT-053
module top(input logic a, b, output logic q);
  initial begin
    q = a;
    #1;
    q = b;
  end
endmodule
