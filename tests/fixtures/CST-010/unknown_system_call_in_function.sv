// sverity-args: --top top
// Lookalike of good_pure_system_call_in_function: $urandom takes its value from the simulator, which the register's dependencies cannot show.
// expect: CST-010 WARNING
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
  function automatic logic [7:0] noisy(input logic [7:0] v);
    return v ^ 8'($urandom);
  endfunction
  always_ff @(posedge clk) q <= noisy(d);
endmodule
