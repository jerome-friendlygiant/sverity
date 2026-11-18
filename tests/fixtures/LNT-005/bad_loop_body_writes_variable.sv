// sverity-args: --top top
// The body writes the loop variable, so its values are not the counting sequence the proof needs.
// expect: LNT-005 WARNING 8
module top(input logic [7:0] d, output logic [3:0] n);
  localparam int unsigned LIMIT = 8;
  always_comb begin
    n = '0;
    for (int i = 0; i < LIMIT; i++) begin
      n += d[i];
      i = i + 2;
    end
  end
endmodule
