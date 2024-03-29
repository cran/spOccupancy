\name{fitted.intPGOcc}

\alias{fitted.intPGOcc}

\title{Extract Model Fitted Values for intPGOcc Object}

\description{
  Method for extracting model fitted values and detection probability values from a fitted single-species integrated occupancy (\code{intPGOcc}) model. 
}

\usage{
\method{fitted}{intPGOcc}(object, \dots)
}

\arguments{
  \item{object}{object of class \code{intPGOcc}.}
  \item{\dots}{currently no additional arguments}
}

\details{
  A method to the generic \code{\link[stats]{fitted}} function to extract fitted values and detection probability values for fitted model objects of class \code{intPGOcc}. 
}

\value{
  A list comprised of

  \item{y.rep.samples}{A list of three-dimensional numeric arrays of fitted values for each individual data source for use in Goodness of Fit assessments.}

  \item{p.samples}{A list of three-dimensional numeric arrays of detection probability values.}
}

\keyword{model}

