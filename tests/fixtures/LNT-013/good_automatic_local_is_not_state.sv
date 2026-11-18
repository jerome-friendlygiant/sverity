// sverity-args: --top top
// An automatic local is created per activation and written before it is read.
// expect-none: LNT-013
module top(input logic [3:0] a, output logic [3:0] y);
  function automatic logic [3:0] f(input logic [3:0] v);
    logic [3:0] s;
    s = v;
    s = s + 1;
    return s;
  endfunction
  always_comb begin
    automatic logic [3:0] t;
    t = f(a);
    y = t;
  end
endmodule
