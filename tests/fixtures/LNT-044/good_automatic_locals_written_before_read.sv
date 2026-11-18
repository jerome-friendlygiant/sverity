// sverity-args: --top top
// Automatic locals written on every path before they are read, and loop counters declared by the for header.
// expect-none: LNT-044
module top(input logic [3:0] a, input logic c, output logic [3:0] y1, y2);
  function automatic logic [3:0] f(input logic [3:0] v);
    logic [3:0] s;
    s = 0;
    for (int i = 0; i < 4; i++) s[i] = v[3-i];
    return s;
  endfunction
  always_comb begin
    automatic logic [3:0] t = a;
    if (c) t = ~t;
    y1 = t;
  end
  assign y2 = f(a);
endmodule
