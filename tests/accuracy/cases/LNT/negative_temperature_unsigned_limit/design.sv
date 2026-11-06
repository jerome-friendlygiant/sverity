module top(input logic signed [7:0] temperature,
  
           input logic [7:0] limit,
  output logic below_limit);

  // The sensor emits negative Celsius values in two's complement.
  // The configured limit is a nonnegative Celsius magnitude.
  always_comb begin
    below_limit = temperature < limit;
  end

endmodule
