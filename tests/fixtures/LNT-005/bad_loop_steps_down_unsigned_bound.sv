// sverity-args: --top top
// The loop steps down, so the values are not bounded below by the start: the comparison with the unsigned bound may see -1.
// expect: LNT-005 WARNING 8
module top(input logic [7:0] d, output logic [3:0] n);
  localparam logic [2:0] LOW = 3'd0;
  always_comb begin
    n = '0;
    for (int i = 7; i >= LOW; i--) n += d[i];
  end
endmodule
