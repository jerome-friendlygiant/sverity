// sverity-args: --top top
// The items cover every value of the selector exactly once: the pragma changes nothing, so it is advisory.
// expect: LNT-083 INFO 8
// expect-none: LNT-083 WARNING
module top(input logic [1:0] s, input logic a, b, c, d, output logic y);
  always_comb begin
    y = 1'b0;
    case (s) // synopsys full_case parallel_case
      2'd0: y = a;
      2'd1: y = b;
      2'd2: y = c;
      2'd3: y = d;
    endcase
  end
endmodule
