// sverity-args: --top top
// The foreach writes every element of the automatic array before the loop that reads it.
// expect-none: LNT-044
module top(input logic [3:0][7:0] w, output logic [7:0] y);
  function automatic logic [7:0] fold(input logic [3:0][7:0] v);
    logic [7:0] parts [4];
    logic [7:0] s;
    foreach (parts[k]) parts[k] = v[k];
    s = '0;
    for (int k = 0; k < 4; k++) s ^= parts[k];
    return s;
  endfunction
  assign y = fold(w);
endmodule
