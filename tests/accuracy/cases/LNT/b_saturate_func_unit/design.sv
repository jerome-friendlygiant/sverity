// Adds two samples and limits the result to a programmable ceiling.
// Interface:
//   a, b     unsigned 8-bit samples
//   cap      ceiling for the sum
//   sum_lim  limited sum
module top (
  input  logic [7:0] a,
  input  logic [7:0] b,
  input  logic [7:0] cap,
  output logic [7:0] sum_lim,
  output logic       clipped
);
  logic [8:0] wide;

  function automatic logic [7:0] limit_sum(input logic [8:0] v, input logic [7:0] ceiling);
    if (v > {1'b0, ceiling})  limit_sum = ceiling;
    else if (v != 9'd0)       limit_sum = v[7:0];
  endfunction

  assign wide = {1'b0, a} + {1'b0, b};

  assign clipped = (wide > {1'b0, cap});

  always_comb sum_lim = limit_sum(wide, cap);
endmodule
