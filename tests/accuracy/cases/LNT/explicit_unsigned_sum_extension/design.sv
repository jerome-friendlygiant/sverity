module top(input logic [7:0] a,
  b,
  output logic [8:0] sum);

  typedef logic [8:0] wide_t;
  function automatic wide_t widen(input logic [7:0] value);
    return wide_t'(value);
  endfunction
  always_comb begin
    sum = widen(a) + widen(b);
  end

endmodule
