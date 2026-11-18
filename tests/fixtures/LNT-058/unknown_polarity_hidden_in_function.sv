// sverity-args: --top top
// The reset is tested through a function, so whether the reset branch runs on low is not read: never silent.
// The function returns r unchanged, so the branch actually runs while rst_n is high.
// expect: LNT-058 WARNING 7
module top(input logic clk, rst_n, d, output logic q);
  function automatic logic active(logic r); return r; endfunction
  always_ff @(posedge clk or negedge rst_n)
    if (active(rst_n)) q <= 1'b0;
    else q <= d;
endmodule
