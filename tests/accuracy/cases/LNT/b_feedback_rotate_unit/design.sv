// Rotating state word with XOR feedback.
// Interface:
//   load   take seed as the new state
//   keep   rotate the current state with coef instead of taking rot_in
//   seed   state loaded when load is set
//   coef   feedback mask
//   rot_in word used when neither load nor keep is set
//   y      current state word
//   y_par  parity of y
//   y_zero y is all zeros
module top (
  input  logic       load,
  input  logic       keep,
  input  logic [7:0] seed,
  input  logic [7:0] coef,
  input  logic [7:0] rot_in,
  output logic [7:0] y,
  output logic       y_par,
  output logic       y_zero
);
  assign y = load ? seed
                  : (keep ? ({y[6:0], y[7]} ^ coef) : rot_in);

  assign y_par  = ^y;
  assign y_zero = (y == 8'h00);
endmodule
