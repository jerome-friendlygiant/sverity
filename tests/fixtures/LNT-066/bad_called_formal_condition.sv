// sverity-args: --top top
// expect: LNT-066 WARNING 12
// expect-count: LNT-066 WARNING 1
module top(input logic clk, reset_n, d, output logic y, safe_y);
 logic q;
 always_ff @(posedge clk or negedge reset_n)
   if (!reset_n) q <= 1'bx;
   else q <= d;
 function automatic logic select_value(input logic enable, value);
   select_value = enable ? value : 1'b0;
 endfunction
 assign y = select_value(q, d);
 assign safe_y = select_value(d, q);
endmodule
