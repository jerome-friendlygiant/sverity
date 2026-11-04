// sverity-args: --top top --sdc @DIR@/async.sdc
// expect: CST-012 ERROR
// revert: return b => return a
// revert-expect-none: CST-012 ERROR
module top(input logic clk_a, clk_b, output logic q);
 logic a,b;
 always_ff @(posedge clk_a) a <= ~a;
 always_ff @(posedge clk_b) b <= ~b;
 function automatic logic value(); return b; endfunction
 assign q = value();
endmodule
